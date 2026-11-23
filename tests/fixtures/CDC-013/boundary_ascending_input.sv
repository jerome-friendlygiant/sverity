module top(input logic launch_clk, sample_clk, input logic [4:5] event_i,
           output logic [1:0] event_o);
  logic [1:0] first_stage;
  always_ff @(posedge sample_clk) begin
    first_stage <= event_i;
    event_o <= first_stage;
  end
endmodule
