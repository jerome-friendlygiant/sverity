// Fixed-priority grant selector for four requesters; req[3] has the
// highest priority. The grant register feeds the crossbar.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [3:0] req,
  output logic [3:0] gnt_q
);
  logic [3:0] gnt_c;

  always_comb begin
    casez (req) // synopsys parallel_case
      4'b1???: gnt_c = 4'b1000;
      4'b?1??: gnt_c = 4'b0100;
      4'b??1?: gnt_c = 4'b0010;
      4'b???1: gnt_c = 4'b0001;
      default: gnt_c = 4'b0000;
    endcase
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) gnt_q <= 4'd0;
    else        gnt_q <= gnt_c;
  end
endmodule
