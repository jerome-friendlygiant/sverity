// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic [7:0] data_i, output logic [8:0] out_o);
 typedef struct packed {logic valid; logic [7:0] data;} word_t;
 word_t payload_q;
 logic [1:0] release_q;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) release_q <= 0; else release_q <= {release_q[0],1'b1};
 always_ff @(posedge clk_i or negedge release_q[1])
   if (!release_q[1]) begin payload_q <= '0; out_o <= 0; end
   else begin payload_q <= {1'b1,data_i}; out_o <= payload_q; end
endmodule
