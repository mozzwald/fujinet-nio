#include <stdbool.h>
void sb_pio_rearm_program_init(bool);
void __real_sb_pio_rearm_program_init(bool enable){sb_pio_rearm_program_init(enable);}
