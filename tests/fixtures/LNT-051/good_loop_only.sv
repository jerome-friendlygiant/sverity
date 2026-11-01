// sverity-args: --top top
// expect-none: LNT-051
module top #(parameter int N = 2) (input logic [N-1:0] a, output logic [N-1:0] y);
  for (genvar i = 0; i < N; i++) begin : g
    assign y[i] = a[i];
  end
endmodule
