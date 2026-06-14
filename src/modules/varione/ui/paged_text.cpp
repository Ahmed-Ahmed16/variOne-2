/*
 * VariOne paged text viewer — see paged_text.h for responsibility.
 */
#include "paged_text.h"
#include "core/scrollableTextArea.h"
#include <globals.h>

void showPagedText(const String &title, const String &body) {
    ScrollableTextArea area(title);
    area.fromString(body);
    area.draw(true);

    // Drain any press that opened this screen so it can't instantly exit.
    check(SelPress);
    check(EscPress);

    while (true) {
        if (check(SelPress) || check(EscPress)) break;
        if (check(PrevPress) || check(UpPress)) area.scrollUp();
        else if (check(NextPress) || check(DownPress)) area.scrollDown();
        area.draw();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
