module top(input logic launch_clk, sample_clk, event_i, output logic event_o);
  logic sync1, sync2;
  always_ff @(posedge sample_clk) begin
    sync1 <= event_i;
    sync2 <= sync1;
  end
  assign event_o = sync2;
endmodule
