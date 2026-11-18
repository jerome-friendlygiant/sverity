// sverity-args: --top top
// The local is set to 1, then written again through a streaming-concatenation target.
// expect-unresolved: LNT-006 9
module top(input logic [3:0] a, input logic [7:0] v, output logic y);
  always_comb begin : blk
    logic [3:0] j;
    j = 4'd1;
    {<<{j}} = a;
    y = v[j];
  end
endmodule
