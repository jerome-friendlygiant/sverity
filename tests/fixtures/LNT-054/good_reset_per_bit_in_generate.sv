// sverity-args: --top top
// A generate loop resets one bit per process; together they reset the whole vector.
// expect-none: LNT-054
module top(input logic clk, rst, input logic [3:0] d, output logic y);
  logic [3:0] v;
  for (genvar i = 0; i < 4; i++) begin : g
    always_ff @(posedge clk) if (rst) v[i] <= 1'b0; else v[i] <= d[i];
  end
  always_comb begin
    if (v[3]) y = 1'b1; else y = 1'b0;
  end
endmodule
