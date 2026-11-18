// PWM output whose duty value comes from one of two sources.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [1:0] src_sel,
  input  logic [7:0] duty_a,
  input  logic [7:0] duty_b,
  output logic       pwm_out
);
  logic [7:0] duty_sel;
  logic [7:0] cnt;

  always_comb begin
    if (src_sel[0])      duty_sel = duty_a;
    else if (src_sel[1]) duty_sel = duty_b;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cnt     <= 8'd0;
      pwm_out <= 1'b0;
    end else begin
      cnt     <= cnt + 8'd1;
      pwm_out <= (cnt < duty_sel);
    end
  end
endmodule
