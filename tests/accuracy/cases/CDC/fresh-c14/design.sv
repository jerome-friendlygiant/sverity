module top(input logic clk_a, clk_b, input logic rst_a_n, rst_b_n, output logic event_b);
  logic phase_a, pulse_a;
  always_ff @(posedge clk_a) if (!rst_a_n) begin phase_a <= 1'b0; pulse_a <= 1'b0; end else begin phase_a <= ~phase_a; pulse_a <= phase_a; end
  always_ff @(posedge clk_b) if (!rst_b_n) event_b <= 1'b0; else event_b <= pulse_a;
endmodule
