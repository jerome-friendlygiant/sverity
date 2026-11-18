// sverity-args: --top top
// a[k] may or may not be a[0]; the whole write after the read makes a[0] a read before write when it is not.
// expect-unresolved: LNT-044 8
module top(input logic [3:0] x, input logic [1:0] k, output logic y);
  logic [3:0] a;
  always_comb begin
    a[k] = x[0];
    y = a[0];
    a = x;
  end
endmodule
