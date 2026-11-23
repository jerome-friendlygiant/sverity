module top(input logic launch_clk, sample_clk, event_i, en, output logic event_o);
  logic first_stage;
  always_ff @(posedge sample_clk) begin
    first_stage <= event_i & en;
    event_o <= first_stage;
  end
endmodule
