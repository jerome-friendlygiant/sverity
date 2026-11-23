// sverity-args: --top top --sdc @DIR@/exclusive_output_delivery.sdc
// expect-none: CDC-013
module top(input logic clk_src, clk_dst, input logic event_i, output logic event_o);
  logic first_stage;
  always_ff @(posedge clk_dst) begin
    first_stage <= event_i;
    event_o <= first_stage;
  end
endmodule
