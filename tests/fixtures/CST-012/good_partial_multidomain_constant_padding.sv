// sverity-args: --top top --sdc @DIR@/partial_multidomain_padding.sdc
// expect-none: CST-012 WARNING
module top(input logic a, b, output logic [2:0] q);
  logic x, y;
  always_ff @(posedge a) x <= ~x;
  always_ff @(posedge b) y <= ~y;
  assign q = {x, y, 1'b0};
endmodule
