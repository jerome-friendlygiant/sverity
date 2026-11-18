// Request/acknowledge controller for a single-outstanding bus master.
// Starts a transfer on start_i, waits for ack_i and retries after a timeout.
module top (
  input  logic clk,
  input  logic rst_n,
  input  logic start_i,
  input  logic ack_i,
  output logic req_o,
  output logic done_o,
  output logic fail_o
);
  typedef enum logic [1:0] {IDLE, WAIT_ACK, DONE, FAIL} state_t;

  state_t      state_q;
  logic [3:0]  tmo_cnt;
  logic [1:0]  retries;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      state_q <= IDLE;
      tmo_cnt <= 4'd0;
      retries <= 2'd0;
    end else begin
      case (state_q)
        IDLE: begin
          tmo_cnt <= 4'd0;
          retries <= 2'd0;
          if (start_i) state_q <= WAIT_ACK;
        end
        WAIT_ACK: begin
          if (ack_i) state_q <= DONE;
          else if (tmo_cnt == 4'd15) begin
            tmo_cnt <= 4'd0;
            retries <= retries + 2'd1;
            if (retries == 2'd3) state_q <= FAIL;
          end else begin
            tmo_cnt <= tmo_cnt + 4'd1;
          end
        end
        DONE:    state_q <= IDLE;
        default: if (!start_i) state_q <= IDLE;
      endcase
    end
  end

  assign req_o  = (state_q == WAIT_ACK);
  assign done_o = (state_q == DONE);
  assign fail_o = (state_q == FAIL);
endmodule
