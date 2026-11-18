// sverity-args: --top top
// expect: LNT-019 WARNING
module top(output logic q);
  initial begin #1; q = 0; end
endmodule
