// PWM output with a selectable gain stage on the requested duty level.
// The ramp counter runs freely; the output is high while ramp < scaled level.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] level,
  input  logic       gain_sel,
  output logic       pwm_o
);
  real        gain;
  logic [7:0] scaled;
  logic [7:0] ramp;

  // gain_sel picks between full scale and three quarters.
  always_comb begin
    gain = gain_sel ? 0.75 : 1.0;
  end

  always_comb begin
    scaled = 8'($rtoi($itor(level) * gain));
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) ramp <= 8'd0;
    else        ramp <= ramp + 8'd1;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) pwm_o <= 1'b0;
    else        pwm_o <= (ramp < scaled);
  end
endmodule
