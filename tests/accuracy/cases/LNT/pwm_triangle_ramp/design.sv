// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned PeriodBits = 8
) (
  input  logic                  clk_i,
  input  logic                  rst_ni,
  input  logic                  tick_i,
  input  logic [PeriodBits-1:0] step_i,
  output logic                  pwm_o,
  output logic [PeriodBits-1:0] duty_o
);

  localparam logic [PeriodBits-1:0] DutyInit = {1'b1, {(PeriodBits-1){1'b0}}};

  logic [PeriodBits-1:0] duty_q;
  logic [PeriodBits-1:0] phase_q;
  logic                  rising_q;
  logic                  pwm_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      phase_q  <= '0;
      duty_q   <= DutyInit;
      rising_q <= 1'b1;
      pwm_q    <= 1'b0;
    end else begin
      phase_q <= phase_q + 1'b1;
      pwm_q   <= (phase_q < duty_q);
      if (tick_i) begin
        if (rising_q) begin
          if (duty_q > ~step_i) begin
            duty_q   <= '1;
            rising_q <= 1'b0;
          end else begin
            duty_q <= duty_q + step_i;
          end
        end else begin
          if (duty_q < step_i) begin
            duty_q   <= '0;
            rising_q <= 1'b1;
          end else begin
            duty_q <= duty_q - step_i;
          end
        end
      end
    end
  end

  assign pwm_o  = pwm_q;
  assign duty_o = duty_q;

endmodule
