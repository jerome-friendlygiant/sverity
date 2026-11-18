// Channel configuration block with active-high reset. The reset image is a
// packed constant defined once; software may overwrite the whole word.
typedef struct packed {
  logic [3:0] burst;
  logic [1:0] mode;
  logic       en;
} cfg_t;

module top (
  input  logic       clk,
  input  logic       rst,
  input  logic       wr_i,
  input  logic [6:0] wdata_i,
  output cfg_t       cfg_o,
  output logic [7:0] timeout_o
);
  localparam cfg_t       CFG_DEFAULT     = '{burst: 4'd8, mode: 2'd1, en: 1'b1};
  localparam logic [7:0] BASE_TIMEOUT    = 8'd20;
  localparam logic [7:0] DEFAULT_TIMEOUT = BASE_TIMEOUT * 8'd2 + 8'd1;

  cfg_t       cfg_q;
  logic [7:0] timeout_q;

  always_ff @(posedge clk or posedge rst) begin
    if (rst) begin
      cfg_q     <= CFG_DEFAULT;
      timeout_q <= DEFAULT_TIMEOUT;
    end else if (wr_i) begin
      cfg_q     <= cfg_t'(wdata_i);
      timeout_q <= {1'b0, wdata_i};
    end
  end

  assign cfg_o     = cfg_q;
  assign timeout_o = timeout_q;
endmodule
