// sverity-args: --top top
// One operand is forced high but the other is below 128: the sum spans 128..382, so some inputs fit and nothing is proven lost.
// A wider target, and a shift whose top operand bit is free, keep the value too.
// expect-none: LNT-036
module top(input logic [7:0] a, b, output logic [7:0] total, output logic [8:0] wide_total, output logic [9:0] wide_shift);
  logic [7:0] hi_a, lo_b;
  assign hi_a = {1'b1, a[6:0]};
  assign lo_b = {1'b0, b[6:0]};
  assign total = hi_a + lo_b;
  assign wide_total = {1'b0, hi_a} + {1'b0, hi_a};
  assign wide_shift = {2'b00, hi_a} << 2;
endmodule
