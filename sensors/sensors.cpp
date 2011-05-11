#include "IntelCameraSOC.h"
namespace android{
extern struct parameters aptina5140soc;
extern struct parameters aptina1040soc;

struct parameters *sensors[] = {
    &aptina5140soc,
    &aptina1040soc,
    NULL
};

}
