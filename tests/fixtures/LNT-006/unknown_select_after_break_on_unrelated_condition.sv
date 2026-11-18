// sverity-args: --top top
// Lookalike of the guarding break: the loop leaves on another signal, which says nothing of the index, so the select can still read 4..7.
// expect-unresolved: LNT-006 11
module top(input logic [7:0] d, input logic [2:0] idx, output logic y);
  logic [3:0] a;
  always_comb begin
    y = 1'b0;
    a = d[3:0];
    for (int i = 0; i < 2; i++) begin
      if (d[7]) break;
      y = a[idx];
    end
  end
endmodule
