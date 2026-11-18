// sverity-args: --top top
// A pointer difference shifted right and divided: still the modular difference, no carry slot exists for a subtraction.
// expect-none: LNT-036
module top(input logic [3:0] wp, rp, output logic [3:0] half, third);
  assign half = (wp - rp) >> 1;
  assign third = (wp - rp) / 4'd3;
endmodule
