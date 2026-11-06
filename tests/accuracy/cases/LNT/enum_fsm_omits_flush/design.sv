module packet_fsm (
  input  logic       clk,
  input  logic       reset_n,
  input  logic       start,
  input  logic       done,
  input  logic       flush,
  output logic       busy,
  output logic       flush_active
);
  typedef enum logic [1:0] {
    IDLE  = 2'b00,
    RUN   = 2'b01,
    FLUSH = 2'b10
  } state_t;

  state_t state, next_state;

  always_comb begin
    next_state = state;
    case (state)
      IDLE: begin
        if (start)
          next_state = RUN;
      end
      RUN: begin
        if (flush)
          next_state = FLUSH;
        else if (done)
          next_state = IDLE;
      end
    endcase
  end

  always_ff @(posedge clk) begin
    if (!reset_n)
      state <= IDLE;
    else
      state <= next_state;
  end

  assign busy = (state == RUN);
  assign flush_active = (state == FLUSH);
endmodule

module top (
  input  logic clk,
  input  logic reset_n,
  input  logic start,
  input  logic done,
  input  logic flush,
  output logic busy,
  output logic flush_active
);
  packet_fsm u_fsm (.*);
endmodule
