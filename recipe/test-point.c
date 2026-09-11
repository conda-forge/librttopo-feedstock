#include <librttopo_geom.h>
#include <stdio.h>

int main(void) {
    RTCTX *ctx = rtgeom_init(NULL, NULL, NULL);
    RTPOINT *point;
    if (!ctx) return 1;
    point = rtpoint_make2d(ctx, 4326, 12.5, -34.25);
    if (!point) return 2;
    if (rtpoint_get_x(ctx, point) != 12.5 || rtpoint_get_y(ctx, point) != -34.25)
        return 3;
    rtpoint_free(ctx, point);
    rtgeom_finish(ctx);
    puts("librttopo installed-library point test passed");
    return 0;
}
