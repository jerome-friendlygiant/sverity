// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned WaitCycles = 200,
  parameter int unsigned RefreshCnt = 8
) (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic start_i,
  output logic cmd_precharge_o,
  output logic cmd_refresh_o,
  output logic cmd_mode_set_o,
  output logic ready_o
);

  typedef enum logic [2:0] {
    S_IDLE,
    S_WAIT,
    S_PRECHARGE,
    S_REFRESH,
    S_MODE_SET,
    S_READY
  } init_state_e;

  init_state_e state_q, state_d;
  logic [$clog2(WaitCycles+1)-1:0] wait_q, wait_d;
  logic [3:0] refresh_q, refresh_d;

  always_comb begin
    state_d         = state_q;
    wait_d          = wait_q;
    refresh_d       = refresh_q;
    cmd_precharge_o = 1'b0;
    cmd_refresh_o   = 1'b0;
    cmd_mode_set_o  = 1'b0;
    ready_o         = 1'b0;
    unique case (state_q)
      S_IDLE: begin
        if (start_i) begin
          wait_d  = ($clog2(WaitCycles+1))'(WaitCycles);
          state_d = S_WAIT;
        end
      end
      S_WAIT: begin
        wait_d = wait_q - 1'b1;
        if (wait_q == '0) state_d = S_PRECHARGE;
      end
      S_PRECHARGE: begin
        cmd_precharge_o = 1'b1;
        refresh_d       = 4'(RefreshCnt);
        state_d         = S_REFRESH;
      end
      S_REFRESH: begin
        cmd_refresh_o = 1'b1;
        refresh_d     = refresh_q - 1'b1;
        if (refresh_q == 4'd1) state_d = S_MODE_SET;
      end
      S_MODE_SET: begin
        cmd_mode_set_o = 1'b1;
        state_d        = S_READY;
      end
      S_READY: ready_o = 1'b1;
      default: state_d = S_IDLE;
    endcase
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q   <= S_IDLE;
      wait_q    <= '0;
      refresh_q <= '0;
    end else begin
      state_q   <= state_d;
      wait_q    <= wait_d;
      refresh_q <= refresh_d;
    end
  end

endmodule
