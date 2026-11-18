// sverity-args: --top top
// Both parts are written before the whole vector is read; the later whole write only replaces them.
// expect-none: LNT-044
module top(input logic [1:0] x, output logic [1:0] y);
  logic [1:0] a;
  always_comb begin
    a[0] = x[0];
    a[1] = x[1];
    y = a;
    a = ~x;
  end
endmodule
