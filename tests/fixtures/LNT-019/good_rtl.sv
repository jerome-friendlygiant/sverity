// sverity-args: --top top
// expect-none: LNT-019
// expect-none: LNT-079
// expect-none: LNT-080
module top #(parameter int N = 4) (input logic clk, input logic [N-1:0] a, output logic [N-1:0] q);
  localparam int M = N;
  always_ff @(posedge clk) begin
    for (int i = 0; i < N; i++) q[i] <= a[i];
    for (int j = 0; j < M; j++) q[j] <= a[j];
  end
endmodule
