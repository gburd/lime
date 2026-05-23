       IDENTIFICATION DIVISION.
       PROGRAM-ID. PAYROLL.
       AUTHOR. JANE SMITH.
       DATE-WRITTEN. 1993-04-12.

       ENVIRONMENT DIVISION.
       CONFIGURATION SECTION.
       SOURCE-COMPUTER. IBM-MAINFRAME.
       OBJECT-COMPUTER. IBM-MAINFRAME.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  EMPLOYEE-RECORD.
           05  EMP-ID         PIC 9(5).
           05  EMP-NAME       PIC X(30).
           05  EMP-RATE       PIC 9(3)V99 USAGE COMP-3.
           05  EMP-HOURS      PIC 9(3)V99 USAGE COMP-3.
           05  EMP-GROSS      PIC 9(7)V99 USAGE COMP-3.
           05  EMP-FLAGS.
               10  FLAG-EXEMPT    PIC X.
                   88  IS-EXEMPT  VALUE "Y".
               10  FLAG-OVERTIME  PIC X.
                   88  HAS-OVERTIME VALUE "Y".

       01  SUMMARY-RECORD REDEFINES EMPLOYEE-RECORD.
           05  SUM-FILLER     PIC X(30).
           05  SUM-PAYABLE    PIC 9(9)V99 USAGE COMP-3.

       01  EMP-COUNT          PIC 9(4) VALUE ZERO.
       01  TOTAL-PAY          PIC 9(9)V99 VALUE ZERO USAGE COMP-3.

       PROCEDURE DIVISION.
       MAIN.
           PERFORM INIT-COUNTERS.
           DISPLAY "Payroll computation started".
           STOP RUN.

       INIT-COUNTERS.
           MOVE 0 TO EMP-COUNT.
           MOVE 0 TO TOTAL-PAY.
