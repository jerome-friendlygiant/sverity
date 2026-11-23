// sverity-args: --top top --sdc @FIXTURES@/CDC-047/primary_input_exclusive.sdc
// expect: CDC-009 INFO
// expect-none: CDC-009 WARNING
module top(input logic clka, clkb, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] s1; logic u; always_ff @(posedge clka) u <= 1'b0;
  always_ff @(posedge clkb) begin s1 <= d; q <= s1; end
endmodule
