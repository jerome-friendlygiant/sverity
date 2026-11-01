// sverity-args: --top top
// expect: LNT-019 WARNING 7
module top #(parameter int N = 4) (input logic clk, input logic [7:0] n, output logic [7:0] q);
  always_ff @(posedge clk) begin
    for (int i = 0; i < N; i++) q[i] <= n[i];
    repeat (N) q <= q;
    for (int k = 0; k < n; k++) q[k] <= 1'b0;
  end
endmodule
