       IDENTIFICATION DIVISION.
       PROGRAM-ID. ARITHMETIC-DEMO.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  X           PIC 9(5)V99 VALUE 100.50.
       01  Y           PIC 9(5)V99 VALUE 25.25.
       01  RESULT      PIC 9(7)V99 USAGE COMP-3.
       01  AVERAGE     PIC 9(5)V99.
       01  POW-RESULT  PIC 9(9).

       PROCEDURE DIVISION.
       MAIN.
           ADD X TO Y GIVING RESULT.
           DISPLAY "X + Y = " RESULT.

           SUBTRACT X FROM Y GIVING RESULT.
           DISPLAY "Y - X = " RESULT.

           MULTIPLY X BY Y GIVING RESULT.
           DISPLAY "X * Y = " RESULT.

           DIVIDE Y INTO X GIVING RESULT.
           DISPLAY "X / Y = " RESULT.

           COMPUTE AVERAGE = (X + Y) / 2.
           DISPLAY "Average = " AVERAGE.

           COMPUTE POW-RESULT = 2 ** 10.
           DISPLAY "2 ** 10 = " POW-RESULT.

           STOP RUN.
