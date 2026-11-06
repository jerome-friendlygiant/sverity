module top (
  input logic adc_clk,
  input logic capture_clk,
  input logic [11:0] adc_pins,
  output logic [11:0] sample,
  output logic sample_parity
);
  logic [11:0] adc_data;
  // The board supplies both clocks for the converter interface.
  always_ff @(posedge adc_clk)
    adc_data <= adc_pins;
  always_ff @(posedge capture_clk) begin
    sample <= adc_data;
    sample_parity <= ^adc_data;
  end
endmodule
