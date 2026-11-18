// expect: LNT-048 WARNING 4
module top(output logic q);
  task initialize; q = 0; endtask
  initial initialize();
endmodule
