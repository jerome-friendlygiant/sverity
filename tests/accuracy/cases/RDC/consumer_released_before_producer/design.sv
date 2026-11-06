module top (
  input  logic clk_i,
  input  logic power_on_ni,
  input  logic [7:0] payload_i,
  output logic [7:0] payload_o,
  output logic       active_o
);
  typedef enum logic [1:0] {SEQ_RESET, SEQ_CONSUMER, SEQ_PRODUCER, SEQ_RUN} seq_t;
  seq_t seq_q;
  logic producer_reset_ni;
  logic consumer_reset_ni;
  logic [7:0] producer_q;
  logic [7:0] consumer_q;

  always_ff @(posedge clk_i or negedge power_on_ni) begin
    if (!power_on_ni) seq_q <= SEQ_RESET;
    else begin
      case (seq_q)
        SEQ_RESET:    seq_q <= SEQ_CONSUMER;
        SEQ_CONSUMER: seq_q <= SEQ_PRODUCER;
        SEQ_PRODUCER: seq_q <= SEQ_RUN;
        default:      seq_q <= SEQ_RUN;
      endcase
    end
  end

  // The consumer is released one cycle before the producer is valid.
  assign consumer_reset_ni = (seq_q != SEQ_RESET);
  assign producer_reset_ni = (seq_q == SEQ_RUN);

  always_ff @(posedge clk_i or negedge producer_reset_ni) begin
    if (!producer_reset_ni) producer_q <= 8'h00;
    else producer_q <= payload_i + 8'h10;
  end

  always_ff @(posedge clk_i or negedge consumer_reset_ni) begin
    if (!consumer_reset_ni) consumer_q <= 8'h00;
    else consumer_q <= producer_q;
  end

  assign payload_o = consumer_q;
  assign active_o = (seq_q == SEQ_RUN);
endmodule
