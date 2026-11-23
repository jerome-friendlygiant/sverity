// sverity-args: --top top --sdc @DIR@/primary_input_async.sdc
// expect: CDC-047 WARNING
module top(input logic clka, clkb, d, output logic [1:0] q);
  logic [1:0] s1, s2;
  always_ff @(posedge clkb) begin s1 <= {d, d}; s2 <= s1; end
  assign q = s2;
endmodule
