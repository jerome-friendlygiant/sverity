// sverity-args: --top top
// slang types `D - 2` in the comparison's unsigned type; the constant 4 lies in the lower half of it, where both readings agree.
// expect: LNT-005 INFO 7
// expect-none: LNT-005 WARNING
module top(input logic [2:0] l, output logic z);
  localparam int D = 6;
  assign z = l >= (D - 2);
endmodule
