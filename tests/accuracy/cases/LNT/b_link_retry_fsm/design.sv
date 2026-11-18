// Frame sender: send, wait for ack, retry on nack.
typedef enum logic [2:0] {
  S_IDLE  = 3'd0,
  S_SEND  = 3'd1,
  S_WAIT  = 3'd2,
  S_RETRY = 3'd3,
  S_DONE  = 3'd4
} state_t;

module top (
  input  logic clk,
  input  logic rst_n,
  input  logic go,
  input  logic ack,
  input  logic nack,
  output logic send_pulse,
  output logic done,
  output logic retrying
);
  state_t state, nxt;

  always_comb begin
    nxt = state;
    case (state)
      S_IDLE: if (go) nxt = S_SEND;
      S_SEND: nxt = S_WAIT;
      S_WAIT: begin
        if (ack)       nxt = S_DONE;
        else if (nack) nxt = S_RETRY;
      end
      S_DONE:  nxt = S_IDLE;
      default: nxt = state;
    endcase
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) state <= S_IDLE;
    else        state <= nxt;
  end

  assign send_pulse = (state == S_SEND);
  assign done       = (state == S_DONE);
  assign retrying   = (state == S_RETRY);
endmodule
