// sverity-args: --top top
// a[1] is read at its previous value if the later write through the run-time index k reaches it: not decided.
// expect-unresolved: LNT-044 8
module top(input logic [3:0] x, input logic [1:0] k, output logic [3:0] y);
  logic [3:0] a;
  always_comb begin
    y = '0;
    y[0] = a[1];
    a[k] = x[0];
  end
endmodule
