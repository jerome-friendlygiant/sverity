// sverity-args: --top top
// revert: i < WORDS => i < LANES
// revert-expect-none: LNT-006
// A child's loop bound comes from a different parameter than the vector it writes; the parent sets them apart.
// expect: LNT-006 ERROR 13
module packer #(parameter int LANES = 2, parameter int WORDS = 2) (
  input  logic [WORDS-1:0] in_bits,
  output logic [LANES-1:0] out_bits
);
  integer i;
  always_comb begin
    out_bits = '0;
    for (i = 0; i < WORDS; i = i + 1) out_bits[i] = in_bits[i];
  end
endmodule
module top(input logic [5:0] d, output logic [3:0] q);
  packer #(.LANES(4), .WORDS(6)) u_packer(.in_bits(d), .out_bits(q));
endmodule
