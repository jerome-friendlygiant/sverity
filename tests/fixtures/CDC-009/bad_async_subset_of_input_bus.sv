// sverity-args: --top top --sdc @FIXTURES@/CDC-009/mixed_input_clock_intent.sdc
// expect: CDC-009 WARNING
// expect-confidence: CDC-009 established
module top(input logic clka, clkb, clkc, input logic [2:0] d, output logic [2:0] q);
  logic [2:0] s1;
  always_ff @(posedge clkb) begin
    s1 <= d;
    q <= s1;
  end
endmodule
