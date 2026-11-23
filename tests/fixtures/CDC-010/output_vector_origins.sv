// sverity-args: --top top --sdc @DIR@/output_vector_origins.sdc
// expect: CDC-010 WARNING
// expect-confidence: CDC-010 unresolved
// expect: CST-012 WARNING
module top(input logic clk_src, clk_dst, input logic [1:0] data,
           output logic [2:0] observed);
  logic [1:0] launched, first_stage, second_stage;
  always_ff @(posedge clk_src) launched <= data;
  always_ff @(posedge clk_dst) begin
    first_stage <= launched;
    second_stage <= first_stage;
  end
  assign observed = {^second_stage, second_stage};
endmodule
