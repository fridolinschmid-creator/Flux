/* input.h -- Tastatur-Eingabe ueber den Linux-Evdev-Layer.
 * Bewusst evdev statt stdin/termios: das ist derselbe Kernel-Layer,
 * unter dem spaeter ein Touch-Digitizer auftaucht (nur mit
 * EV_ABS-Events statt EV_KEY) -- die Shell ist damit nicht an eine
 * PC-Tastatur gebunden, sondern an "irgendein evdev-Geraet".
 */
#ifndef FLUX_INPUT_H
#define FLUX_INPUT_H

typedef struct {
    int fd;
} flux_input_t;

/* Sucht automatisch das erste evdev-Geraet, das Tasten liefert
 * (/dev/input/event0 .. event31). Gibt 0 bei Erfolg zurueck. */
int flux_input_open(flux_input_t *in);
void flux_input_close(flux_input_t *in);

/* Liefert einen Dateideskriptor zum Pollen (select/poll). */
int flux_input_fd(flux_input_t *in);

/* Liest anstehende Events. ch wird gesetzt, wenn ein druckbares
 * Zeichen erkannt wurde (Buchstabe/Ziffer/Leerzeichen).
 * Rueckgabe: 'c' (Zeichen), 'B' (Backspace), 'E' (Enter), 0 (nichts). */
char flux_input_poll(flux_input_t *in, char *ch);

#endif
