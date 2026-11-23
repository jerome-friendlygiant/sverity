// sverity-args: --top top
// expect: CDC-010 WARNING
// expect-confidence: CDC-010 unresolved
// No input clock contract establishes the provenance of the reconverging bits.
module top(input logic clk, input logic [1:0] data, output logic combined);
  logic [1:0] first_stage, second_stage;
  always_ff @(posedge clk) begin
    first_stage <= data;
    second_stage <= first_stage;
    combined <= &second_stage;
  end
endmodule
