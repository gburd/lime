       IDENTIFICATION DIVISION.
       PROGRAM-ID. CONTROL-FLOW-DEMO.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  COUNTER     PIC 9(3) VALUE ZERO.
       01  STATUS-CD   PIC X.
           88  STATUS-OK   VALUE "Y".
           88  STATUS-FAIL VALUE "N".

       PROCEDURE DIVISION.
       MAIN-LOGIC.
           PERFORM RESET.
           PERFORM PROCESS-LOOP UNTIL COUNTER >= 100.
           IF STATUS-OK
               DISPLAY "Done normally"
           ELSE
               DISPLAY "Done with errors"
           END-IF.

           EVALUATE COUNTER
               WHEN 0
                   DISPLAY "no work"
               WHEN OTHER
                   DISPLAY "processed " COUNTER " items"
           END-EVALUATE.
           STOP RUN.

       RESET.
           MOVE 0 TO COUNTER.
           SET STATUS-OK TO TRUE.
           CONTINUE.

       PROCESS-LOOP.
           ADD 1 TO COUNTER.
           IF COUNTER > 50
               SET STATUS-FAIL TO TRUE
               EXIT
           END-IF.
