// sverity-args: --top top
// The local is set to 1, then written again as one operand of a concatenation target: its value at the select is the sum's low bits.
// expect-unresolved: LNT-006 9
module top(input logic [3:0] a, b, input logic [7:0] v, output logic y, output logic co);
  always_comb begin : blk
    logic [3:0] j;
    j = 4'd1;
    {co, j} = a + b;
    y = v[j];
  end
endmodule
