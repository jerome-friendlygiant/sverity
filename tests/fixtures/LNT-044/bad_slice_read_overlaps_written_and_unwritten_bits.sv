// sverity-args: --top top
// a[2:1] is read: a[1] was written, a[2] is only written afterwards.
// expect: LNT-044 WARNING 8
module top(input logic [3:0] x, output logic [1:0] y);
  logic [3:0] a;
  always_comb begin
    a[1:0] = x[1:0];
    y = a[2:1];
    a[3:2] = x[3:2];
  end
endmodule
