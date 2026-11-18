// Three-state sequencer (idle, run, done) whose state can be reloaded from
// a saved context word when a task is resumed.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       go,
  input  logic       finish,
  input  logic       restore,
  input  logic [1:0] restore_state,
  output logic [1:0] state_o,
  output logic       busy
);
  logic [1:0] state_q;
  logic [1:0] next_c;

  always_comb begin
    next_c = state_q;
    case (state_q) // synopsys full_case
      2'b00: if (go)     next_c = 2'b01;
      2'b01: if (finish) next_c = 2'b10;
      2'b10:             next_c = 2'b00;
    endcase
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)       state_q <= 2'b00;
    else if (restore) state_q <= restore_state;
    else              state_q <= next_c;
  end

  assign state_o = state_q;
  assign busy    = (state_q == 2'b01);
endmodule
