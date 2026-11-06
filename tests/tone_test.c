#include "speaker_tone.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	unsigned frequencies[] = {200, 500, 750, 1000, 2000};
	unsigned peaks[] = {0, 1, 64, 512};
	for (unsigned f = 0; f < sizeof(frequencies)/sizeof(frequencies[0]); f++) {
		for (unsigned a = 0; a < sizeof(peaks)/sizeof(peaks[0]); a++) {
			uint32_t phase = 0;
			uint32_t step = ((uint64_t)frequencies[f] << 32) / 16000;
			int positive = 0, negative = 0;
			for (unsigned n = 0; n < 16000; n++, phase += step) {
				int sample = speaker_tone_sample(phase, n, peaks[a]);
				assert(abs(sample) <= (int)peaks[a]);
				if (n == 0 || n >= 15872) { assert(sample == 0); }
				if (n >= 256 && n < 15616) {
					double expected = peaks[a] * sin(2 * 3.141592653589793 * frequencies[f] * n / 16000);
					assert(fabs(sample - expected) < 5);
					positive |= sample > 0;
					negative |= sample < 0;
				}
			}
			if (peaks[a] >= 64) { assert(positive && negative); }
		}
	}
	puts("PASS: tone polarity, peak bounds, frequency, fade and digital silence");
	return 0;
}
