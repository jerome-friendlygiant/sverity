// sverity-args: --top top
// Bad lookalike: a non-constant loop bound cannot be unrolled, so the reset value stays undecided ('?') and RDC-025 is Unresolved.
// expect-unresolved: RDC-025
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clk, rst_n, input logic [2:0] n, input logic [3:0] d, output logic [3:0] y);
  logic [3:0] v;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      for (int i = 0; i < n; i++) v[i] <= 1'b0;
    end else begin
      v <= d;
    end
  assign y = v;
endmodule
