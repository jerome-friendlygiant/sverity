// sverity-args: --top top
// expect: STY-004 INFO 7
module child(input logic a, output logic y);
  assign y = a;
endmodule
module top(input logic a, output logic y);
  child u0 (.*);
endmodule
