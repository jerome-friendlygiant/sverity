// sverity-args: --top top
// `disable other` names a block that is not running, so it leaves nothing: the select after it can still read 4..7.
// expect-unresolved: LNT-006 11
module top(input logic [7:0] d, input logic [2:0] idx, output logic y);
  logic [3:0] a;
  always_comb begin
    y = 1'b0;
    a = d[3:0];
    begin : other y = d[7]; end
    if (idx >= 3'd4) disable other;
    y = a[idx];
  end
endmodule
