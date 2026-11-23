// sverity-args: --top top --sdc @DIR@/primary_input_exclusive.sdc
// expect: CDC-047 INFO
// expect-none: CDC-047 WARNING
module top(input logic clka, clkb, d, output logic [1:0] q);
  logic a1, a2, b1, b2;
  always_ff @(posedge clkb) begin a1 <= d; a2 <= a1; b1 <= d; b2 <= b1; end
  assign q = {a2, b2};
endmodule
