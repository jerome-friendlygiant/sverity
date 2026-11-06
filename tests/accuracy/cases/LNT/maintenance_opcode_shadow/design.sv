module top (
  input logic clk,
  input logic rst_n,
  input logic command_valid,
  input logic [7:0] opcode,
  output logic start_transfer,
  output logic clear_error
);
  typedef struct packed {
    logic start;
    logic clear;
  } action_t;
  action_t action;
  // AF acknowledges the sticky error; other A-class commands start transfers.
  always_comb begin
    action = '0;
    casez (opcode)
      8'b1010????: action.start = 1'b1;
      8'hAF: action.clear = 1'b1;
      default: action = '0;
    endcase
  end
  always_ff @(posedge clk) begin
    if (!rst_n) begin
      start_transfer <= 1'b0;
      clear_error <= 1'b0;
    end else begin
      start_transfer <= command_valid && action.start;
      clear_error <= command_valid && action.clear;
    end
  end
endmodule
