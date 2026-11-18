// sverity-args: --top top
// `i < 4` is true in every copy of the generate loop, so the test is no branch: each copy certainly resets its bit.
// expect-none: LNT-054
module top(input logic clk, rst, input logic [3:0] d, output logic y);
  logic [3:0] v;
  for (genvar i = 0; i < 4; i++) begin : g
    always_ff @(posedge clk) if (rst) begin if (i < 4) v[i] <= 1'b0; end else v[i] <= d[i];
  end
  always_comb begin
    if (v[3]) y = 1'b1; else y = 1'b0;
  end
endmodule
