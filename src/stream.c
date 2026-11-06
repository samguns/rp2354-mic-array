#include "stream.h"
#include "capture.h"
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <string.h>

#if PCM_SAMPLE_RATE == 48000u
/* High-rate recording uses UAC2. Keep shell snapshots independent of the
 * legacy bulk streaming path, which cannot sustain this profile reliably. */
void stream_push(const int16_t *pcm,uint32_t sequence)
{ARG_UNUSED(pcm);ARG_UNUSED(sequence);}
int stream_run(const struct shell *sh,unsigned seconds)
{
 ARG_UNUSED(seconds);
 shell_error(sh,"Continuous 48 kHz recording requires UAC2; use tools/uac_wasapi.py. CDC snapshots remain available.");
 return -ENOTSUP;
}
#else
struct packet {
	uint32_t sequence;
	int16_t pcm[CAPTURE_FRAMES * MIC_CHANNELS];
};
K_MSGQ_DEFINE(stream_queue, sizeof(struct packet), 16, 4);
static atomic_t active, overflow;

void stream_push(const int16_t *pcm, uint32_t sequence)
{
	if (!atomic_get(&active)) { return; }
	struct packet packet = {.sequence = sequence};
	memcpy(packet.pcm, pcm, sizeof(packet.pcm));
	if (k_msgq_put(&stream_queue, &packet, K_NO_WAIT)) {
		atomic_inc(&overflow);
	}
}

/* The shell command owns its transport until it returns. Write bytes directly
 * so no printf escaping, newline conversion or prompt enters binary packets.
 * Bound USB backpressure; capture never waits for this consumer. */
static int write_bytes(const struct shell *sh, const void *data, size_t size)
{
	const uint8_t *p = data;
	int64_t deadline = k_uptime_get() + 2000;
	while (size) {
		size_t sent = 0;
		int ret = sh->iface->api->write(sh->iface, p, size, &sent);
		if (ret) { return ret; }
		p += sent; size -= sent;
		if (k_uptime_get() > deadline) { return -ETIMEDOUT; }
		if (!sent) { k_msleep(1); }
	}
	return 0;
}

static int send_packet(const struct shell *sh, const struct packet *packet,
		       uint32_t frames, uint32_t error)
{
	uint8_t header[36];
	memcpy(header, "P2PCM001", 8);
	sys_put_le32(packet->sequence, header + 8);
	sys_put_le32(capture_stats.sample_rate, header + 12);
	sys_put_le32(frames, header + 16);
	sys_put_le32(atomic_get(&capture_stats.dropped), header + 20);
	sys_put_le32(atomic_get(&capture_stats.faults), header + 24);
	sys_put_le32(error, header + 28);
	sys_put_le32(crc32_ieee((const uint8_t *)packet->pcm, frames * 8), header + 32);
	int ret = write_bytes(sh, header, sizeof(header));
	return ret ? ret : write_bytes(sh, packet->pcm, frames * 8);
}

int stream_run(const struct shell *sh, unsigned seconds)
{
	struct packet packet = {0};
	k_msgq_purge(&stream_queue);
	atomic_clear(&overflow);
	atomic_set(&active, 1);
	uint32_t count = (seconds * PCM_SAMPLE_RATE + CAPTURE_FRAMES - 1) / CAPTURE_FRAMES;
	int ret = 0;
	uint32_t next_sequence = 0;
	for (uint32_t i = 0; i < count; i++) {
		if (k_msgq_get(&stream_queue, &packet, K_MSEC(500))) {
			ret = -ETIMEDOUT; break;
		}
		if (atomic_get(&overflow) || atomic_get(&capture_stats.faults)) {
			ret = -EIO; break;
		}
		uint32_t frames = MIN(CAPTURE_FRAMES, seconds * PCM_SAMPLE_RATE - i * CAPTURE_FRAMES);
		ret = send_packet(sh, &packet, frames, 0);
		if (ret) { break; }
		next_sequence = packet.sequence + 1;
	}
	atomic_clear(&active);
	packet.sequence = next_sequence;
	/* A terminal packet is mandatory for a valid recording. */
	int end_ret = send_packet(sh, &packet, 0, ret != 0 || atomic_get(&overflow));
	return ret ? ret : end_ret;
}

#endif
