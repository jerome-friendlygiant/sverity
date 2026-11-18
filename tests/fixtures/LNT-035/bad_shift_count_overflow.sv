// sverity-args: --top top
// revert: a << 40 => a << 4
// revert-expect-none: LNT-035
// expect: LNT-035 WARNING
module top(input logic [7:0] a, output logic [7:0] y);
  assign y = a << 40;
endmodule
