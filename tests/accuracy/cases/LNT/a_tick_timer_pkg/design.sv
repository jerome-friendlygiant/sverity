package timer_pkg;
  localparam int TICK_W = 12;
  typedef enum logic [1:0] {T_IDLE, T_RUN, T_DONE} tstate_e;
  function automatic logic [TICK_W-1:0] last_tick(input logic [TICK_W-1:0] period);
    return period - TICK_W'(1);
  endfunction
endpackage

// Free-running tick generator: one pulse every `period` clocks.
module tick_gen
  import timer_pkg::*;
(
  input  logic              clk,
  input  logic              rst_n,
  input  logic [TICK_W-1:0] period,
  output logic              tick
);
  logic [TICK_W-1:0] cnt_q;

  assign tick = (cnt_q == last_tick(period));

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)    cnt_q <= '0;
    else if (tick) cnt_q <= '0;
    else           cnt_q <= cnt_q + TICK_W'(1);
  end
endmodule

// One-shot timer: runs from enable until the first tick.
module top
  import timer_pkg::*;
(
  input  logic              clk,
  input  logic              rst_n,
  input  logic              en,
  input  logic [TICK_W-1:0] period,
  output logic              tick,
  output tstate_e           state
);
  tstate_e nxt;

  tick_gen u_tick (.*);

  always_comb begin
    nxt = state;
    case (state)
      T_IDLE:  if (en) nxt = T_RUN;
      T_RUN:   if (tick) nxt = T_DONE;
      T_DONE:  if (!en) nxt = T_IDLE;
      default: nxt = T_IDLE;
    endcase
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) state <= T_IDLE;
    else        state <= nxt;
  end
endmodule
