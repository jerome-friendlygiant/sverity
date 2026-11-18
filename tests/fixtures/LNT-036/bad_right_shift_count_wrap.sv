// sverity-args: --top top
// revert: x+3'd1 => x+4'd1
// revert-expect-none: LNT-036
// expect: LNT-036 WARNING
module top(input logic [2:0] x, output logic [7:0] y);
  assign y = 8'hff >> (x+3'd1);
endmodule
