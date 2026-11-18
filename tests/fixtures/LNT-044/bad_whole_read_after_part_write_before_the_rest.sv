// sverity-args: --top top
// a[0] is written, then the whole vector is read, then a[1] is written: the read sees the previous value of a[1].
// expect: LNT-044 WARNING 9
// expect-confidence: LNT-044 established 9
module top(input logic [1:0] x, output logic [1:0] y);
  logic [1:0] a;
  always_comb begin
    a[0] = x[0];
    y = a;
    a[1] = x[1];
  end
endmodule
